import json
import os
from datetime import datetime
from typing import Dict, List, Optional, Tuple, Any
from models.order import Order
from services.product_manager import ProductManager
from services.user_manager import UserManager


class OrderManager:
    def __init__(self, product_manager: ProductManager, user_manager: UserManager, data_file: str = None):
        if data_file is None:
            # 使用相对路径，确保在不同操作系统上可移植
            current_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            self.data_file: str = os.path.join(current_dir, "data", "orders.json")
        else:
            self.data_file: str = data_file
        self.product_manager: ProductManager = product_manager
        self.user_manager: UserManager = user_manager
        self.orders: Dict[str, Order] = {}
        self.load_orders()
    
    def load_orders(self) -> None:
        """从文件加载订单数据 - 修复：恢复完整的价格快照信息"""
        if os.path.exists(self.data_file):
            try:
                with open(self.data_file, 'r', encoding='utf-8') as f:
                    orders_data: List[Dict[str, Any]] = json.load(f)
                    for order_data in orders_data:
                        try:
                            order = Order(
                                order_data['customer_id'],
                                order_data['product_id'],
                                order_data['quantity'],
                                self.product_manager
                            )
                            # 恢复订单属性
                            order._order_id = order_data['order_id']
                            order._status = order_data['status']
                            order._total_amount = order_data['total_amount']
                            
                            # 修复：恢复完整的价格快照信息
                            order._initial_product_price = order_data.get('initial_product_price', 0.0)
                            order._initial_product_name = order_data.get('initial_product_name', "")
                            order._initial_product_description = order_data.get('initial_product_description', "")
                            order._initial_merchant_id = order_data.get('initial_merchant_id', "")
                            
                            # 恢复订单日期
                            if 'order_date' in order_data:
                                order._order_date = datetime.strptime(order_data['order_date'], '%Y-%m-%d %H:%M:%S')
                            self.orders[order.order_id] = order
                        except Exception as e:
                            print(f"加载订单数据失败: {order_data.get('order_id', 'Unknown')}, 错误: {e}")
                            continue
            except Exception as e:
                print(f"加载订单数据文件失败: {e}")
                self.orders = {}
    
    def save_orders(self) -> None:
        """保存订单数据到文件 - 修复：包含完整的价格快照信息"""
        orders_data: List[Dict[str, Any]] = []
        for order in self.orders.values():
            order_dict: Dict[str, Any] = {
                'order_id': order.order_id,
                'order_date': order.order_date.strftime('%Y-%m-%d %H:%M:%S'),
                'customer_id': order.customer_id,
                'product_id': order.product_id,
                'quantity': order.quantity,
                'status': order.status,
                'total_amount': order.total_amount,
                # 修复：保存完整的价格快照信息
                'initial_product_price': order._initial_product_price,
                'initial_product_name': order._initial_product_name,
                'initial_product_description': order._initial_product_description,
                'initial_merchant_id': order._initial_merchant_id
            }
            orders_data.append(order_dict)
        
        # 确保目录存在
        os.makedirs(os.path.dirname(self.data_file), exist_ok=True)
        
        with open(self.data_file, 'w', encoding='utf-8') as f:
            json.dump(orders_data, f, ensure_ascii=False, indent=2)
    
    def create_order(self, customer_id: str, product_id: str, quantity: int) -> Tuple[Optional[Order], str]:
        """创建订单"""
        # 检查订单数量是否有效
        if quantity <= 0:
            return None, "商品数量必须为正数"
        
        # 检查商品是否存在且可购买
        product = self.product_manager.get_product(product_id)
        if not product or not product.is_active:
            return None, "商品不存在或已下架"
        
        # 检查库存是否足够
        if product.stock_quantity < quantity:
            return None, "库存不足"
        
        # 创建订单
        order = Order(customer_id, product_id, quantity, self.product_manager)
        self.orders[order.order_id] = order
        
        # 减少库存
        self.product_manager.update_stock(product_id, -quantity)
        
        # 保存订单
        self.save_orders()
        
        return order, "订单创建成功"
    
    def get_order(self, order_id: str) -> Optional[Order]:
        """根据ID获取订单"""
        return self.orders.get(order_id)
    

    
    def get_orders_by_customer(self, customer_id: str) -> List[Order]:
        """获取指定顾客的所有订单"""
        return [o for o in self.orders.values() if o.customer_id == customer_id]
    
    def get_orders_by_merchant(self, merchant_id: str) -> List[Order]:
        """获取指定商家的所有订单"""
        return [o for o in self.orders.values() if o.merchant_id == merchant_id]
    
    def process_order(self, order_id: str, action: str) -> Tuple[bool, str]:
        """处理订单（接受、完成、取消）"""
        order = self.get_order(order_id)
        if not order:
            return False, "订单不存在"
        
        success = False
        message = ""
        
        if action == "accept":
            success = order.accept()
            message = "订单已接受" if success else "操作失败"
        elif action == "reject":
            success = order.reject()  # 使用 reject 方法而不是 cancel
            if success:
                # 拒绝订单时恢复库存
                self.product_manager.update_stock(order.product_id, order.quantity)
            message = "订单已拒绝" if success else "操作失败"
        elif action == "complete":
            success = order.complete()
            message = "订单已完成" if success else "操作失败"
        elif action == "cancel":
            success = order.cancel()
            if success:
                # 取消订单时恢复库存
                self.product_manager.update_stock(order.product_id, order.quantity)
            message = "订单已取消" if success else "操作失败"
        else:
            message = "无效的操作"
        
        if success:
            self.save_orders()
        
        return success, message
    
    def exchange_contact_info(self, order_id: str) -> Tuple[Optional[Dict[str, Any]], str]:
        """交换联系方式（在订单接受后）"""
        order = self.get_order(order_id)
        if not order:
            return None, "订单不存在"
        
        # 检查订单状态，已接受或已完成的订单才能交换联系方式
        if order.status not in ["accepted", "completed"]:
            return None, "只有已接受或已完成的订单才能交换联系方式"
        
        # 获取顾客和商家信息
        customer = self.user_manager.get_user_by_id(order.customer_id)
        merchant = self.user_manager.get_user_by_id(order.merchant_id)
        
        if not customer or not merchant:
            return None, "用户信息不完整"
        
        # 返回双方联系方式
        contact_info: Dict[str, Any] = {
            'customer': {
                'name': customer.username,
                'phone': customer.phone_number,
                'address': customer.shipping_address
            },
            'merchant': {
                'name': merchant.username,
                'shop': merchant.shop_name,
                'contact': merchant.contact_info
            }
        }
        
        return contact_info, "联系方式交换成功"