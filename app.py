from typing import Optional, List, Dict, Any
from services.user_manager import UserManager
from services.product_manager import ProductManager
from services.order_manager import OrderManager
from models.user import User
from models.product import Product
from models.order import Order


class OnlineMallApp:
    def __init__(self):
        # 初始化各管理器
        self.user_manager: UserManager = UserManager()
        self.product_manager: ProductManager = ProductManager()
        self.order_manager: OrderManager = OrderManager(self.product_manager, self.user_manager)
        self.current_user: Optional[User] = None
    
    def display_menu(self) -> None:
        """显示主菜单"""
        print("\n======= 网络商场系统 =======")
        if not self.current_user:
            print("1. 用户注册")
            print("2. 用户登录")
            print("0. 退出系统")
        else:
            print(f"欢迎回来，{self.current_user.username}！")
            print(f"角色：{self.current_user.role}")
            if self.current_user.role == 'merchant':
                self.display_merchant_menu()
            else:
                self.display_customer_menu()
            print("0. 退出登录")
        print("===========================")
    
    def display_merchant_menu(self) -> None:
        """显示商家菜单"""
        print("\n----- 商家功能 -----")
        print("3. 添加商品")
        print("4. 管理商品")
        print("5. 查看订单")
        print("6. 处理订单")
        print("7. 查看库存")
    
    def display_customer_menu(self) -> None:
        """显示顾客菜单"""
        print("\n----- 顾客功能 -----")
        print("3. 浏览商品")
        print("4. 搜索商品")
        print("5. 购买商品")
        print("6. 查看订单")
        print("7. 联系卖家")
    
    def register_user(self) -> None:
        """用户注册"""
        print("\n----- 用户注册 -----")
        role = input("请选择注册类型 (1: 顾客, 2: 商家): ")
        
        username = input("请输入用户名: ")
        password = input("请输入密码: ")
        email = input("请输入邮箱: ")
        
        kwargs = {}
        if role == '1':
            kwargs['shipping_address'] = input("请输入收货地址: ")
            kwargs['phone_number'] = input("请输入手机号码: ")
            user, message = self.user_manager.register_user(username, password, email, 'customer', **kwargs)
        else:
            kwargs['shop_name'] = input("请输入店铺名称: ")
            kwargs['contact_info'] = input("请输入联系信息: ")
            user, message = self.user_manager.register_user(username, password, email, 'merchant', **kwargs)
        
        print(f"\n{message}")
        if user:
            print(f"注册成功！用户ID: {user.user_id}")
    
    def login_user(self) -> None:
        """用户登录"""
        print("\n----- 用户登录 -----")
        username = input("请输入用户名: ")
        password = input("请输入密码: ")
        
        user, message = self.user_manager.login_user(username, password)
        print(f"\n{message}")
        
        if user:
            self.current_user = user
            print(f"登录成功！欢迎回来，{user.username}！")
    
    def logout_user(self) -> None:
        """用户登出"""
        if self.current_user:
            self.current_user.logout()
            self.current_user = None
            print("已成功登出")
    
    def run(self) -> None:
        """运行应用"""
        while True:
            self.display_menu()
            choice = input("请选择操作: ")
            
            if not self.current_user:
                if choice == '1':
                    self.register_user()
                elif choice == '2':
                    self.login_user()
                elif choice == '0':
                    print("谢谢使用，再见！")
                    break
                else:
                    print("无效的选择，请重试")
            else:
                if choice == '0':
                    self.logout_user()
                elif self.current_user.role == 'merchant':
                    self.handle_merchant_actions(choice)
                else:
                    self.handle_customer_actions(choice)
    
    def handle_merchant_actions(self, choice: str) -> None:
        """处理商家操作"""
        if choice == '3':
            self.add_product()
        elif choice == '4':
            self.manage_products()
        elif choice == '5':
            self.view_merchant_orders()
        elif choice == '6':
            self.process_merchant_order()
        elif choice == '7':
            self.view_inventory()
        else:
            print("无效的选择，请重试")
    
    def handle_customer_actions(self, choice: str) -> None:
        """处理顾客操作"""
        if choice == '3':
            self.browse_products()
        elif choice == '4':
            self.search_products()
        elif choice == '5':
            self.purchase_product()
        elif choice == '6':
            self.view_customer_orders()
        elif choice == '7':
            self.contact_seller()
        else:
            print("无效的选择，请重试")
    
    # 商家功能方法
    def add_product(self) -> None:
        """添加商品"""
        print("\n----- 添加商品 -----")
        name = input("请输入商品名称: ")
        description = input("请输入商品描述: ")
        price = float(input("请输入商品价格: "))
        stock = int(input("请输入库存数量: "))
        
        product, message = self.current_user.add_product(self.product_manager, name, description, price, stock)
        print(f"\n{message}")
        if product:
            print(f"商品ID: {product.product_id}")
    
    def manage_products(self) -> None:
        """管理商品"""
        print("\n----- 商品管理 -----")
        products = self.current_user.manage_products(self.product_manager)
        
        if not products:
            print("您还没有添加任何商品")
            return
        
        for i, product in enumerate(products, 1):
            print(f"\n{i}. {product}")
        
        choice = input("\n请选择要操作的商品序号 (0 退出): ")
        if choice == '0':
            return
        
        try:
            index = int(choice) - 1
            if 0 <= index < len(products):
                self.product_management_menu(products[index])
            else:
                print("无效的选择")
        except:
            print("请输入有效的数字")
    
    def product_management_menu(self, product: Product) -> None:
        """商品管理菜单"""
        while True:
            print(f"\n----- 管理商品: {product.name} -----")
            print("1. 更新商品信息")
            print("2. 补充库存")
            print("3. 上架/下架商品")
            print("0. 返回")
            
            choice = input("请选择操作: ")
            
            if choice == '1':
                self.update_product_info(product)
            elif choice == '2':
                quantity = int(input("请输入要补充的库存数量: "))
                success, message = self.product_manager.update_stock(product.product_id, quantity)
                print(f"\n{message}")
            elif choice == '3':
                action = "上架" if not product.is_active else "下架"
                confirm = input(f"确定要{action}该商品吗？(y/n): ")
                if confirm.lower() == 'y':
                    success, message = self.product_manager.toggle_product_status(product.product_id, not product.is_active)
                    print(f"\n{message}")
            elif choice == '0':
                break
            else:
                print("无效的选择")
    
    def update_product_info(self, product: Product) -> None:
        """更新商品信息"""
        print("\n----- 更新商品信息 -----")
        print("留空表示不修改该字段")
        
        name = input(f"商品名称 [{product.name}]: ")
        description = input(f"商品描述 [{product.description}]: ")
        price_input = input(f"商品价格 [{product.price}]: ")
        
        kwargs = {}
        if name:
            kwargs['name'] = name
        if description:
            kwargs['description'] = description
        if price_input:
            kwargs['price'] = float(price_input)
        
        if kwargs:
            success, message = self.product_manager.update_product(product.product_id, **kwargs)
            print(f"\n{message}")
        else:
            print("未做任何修改")
    
    def view_merchant_orders(self) -> None:
        """查看商家订单"""
        print("\n----- 我的订单 -----")
        orders = self.order_manager.get_orders_by_merchant(self.current_user.merchant_id)
        
        if not orders:
            print("暂无订单")
            return
        
        for i, order in enumerate(orders, 1):
            print(f"\n{i}. {order}")
    
    def process_merchant_order(self) -> None:
        """处理订单"""
        orders = self.order_manager.get_orders_by_merchant(self.current_user.merchant_id)
        pending_orders = [o for o in orders if o.status == 'pending']
        
        if not pending_orders:
            print("\n暂无待处理的订单")
            return
        
        print("\n----- 待处理订单 -----")
        for i, order in enumerate(pending_orders, 1):
            print(f"\n{i}. {order}")
        
        choice = input("\n请选择要处理的订单序号 (0 退出): ")
        if choice == '0':
            return
        
        try:
            index = int(choice) - 1
            if 0 <= index < len(pending_orders):
                order = pending_orders[index]
                action = input("请选择操作 (1: 接受, 2: 拒绝): ")
                if action == '1':
                    success, message = self.order_manager.process_order(order.order_id, "accept")
                    print(f"\n{message}")
                    # 接受订单后提示是否交换联系方式
                    if success:
                        confirm = input("是否现在交换联系方式？(y/n): ")
                        if confirm.lower() == 'y':
                            contact_info, msg = self.order_manager.exchange_contact_info(order.order_id)
                            if contact_info:
                                print("\n----- 顾客联系方式 -----")
                                print(f"姓名: {contact_info['customer']['name']}")
                                print(f"电话: {contact_info['customer']['phone']}")
                                print(f"地址: {contact_info['customer']['address']}")
                elif action == '2':
                    success, message = self.order_manager.process_order(order.order_id, "cancel")
                    print(f"\n{message}")
            else:
                print("无效的选择")
        except:
            print("请输入有效的数字")
    
    def view_inventory(self) -> None:
        """查看库存"""
        print("\n----- 库存查询 -----")
        products = self.current_user.view_inventory(self.product_manager)
        
        if not products:
            print("暂无商品")
            return
        
        for product in products:
            print(f"\n商品: {product.name}")
            print(f"库存: {product.stock_quantity}")
            print(f"状态: {'上架' if product.is_active else '下架'}")
    
    # 顾客功能方法
    def browse_products(self) -> None:
        """浏览商品"""
        print("\n----- 商品列表 -----")
        products = self.current_user.browse_products(self.product_manager)
        
        if not products:
            print("暂无商品")
            return
        
        for i, product in enumerate(products, 1):
            print(f"\n{i}. {product}")
    
    def search_products(self) -> None:
        """搜索商品"""
        keyword = input("\n请输入搜索关键词: ")
        products = self.current_user.search_products(self.product_manager, keyword)
        
        if not products:
            print("\n未找到匹配的商品")
            return
        
        print("\n----- 搜索结果 -----")
        for i, product in enumerate(products, 1):
            print(f"\n{i}. {product}")
    
    def purchase_product(self) -> None:
        """购买商品"""
        self.browse_products()
        
        product_id = input("\n请输入要购买的商品ID: ")
        product = self.product_manager.get_product(product_id)
        
        if not product or not product.is_active:
            print("商品不存在或已下架")
            return
        
        print(f"\n您选择的商品: {product.name}")
        print(f"价格: ¥{product.price}")
        print(f"库存: {product.stock_quantity}")
        
        try:
            quantity = int(input("请输入购买数量: "))
            if quantity <= 0:
                print("购买数量必须大于0")
                return
            
            order, message = self.current_user.place_order(self.order_manager, product_id, quantity)
            print(f"\n{message}")
            if order:
                print(f"订单ID: {order.order_id}")
                print("请等待商家确认订单")
        except:
            print("请输入有效的数字")
    
    def view_customer_orders(self) -> None:
        """查看顾客订单"""
        print("\n----- 我的订单 -----")
        orders = self.current_user.view_purchases(self.order_manager)
        
        if not orders:
            print("您还没有任何订单")
            return
        
        for i, order in enumerate(orders, 1):
            print(f"\n{i}. {order}")
    
    def contact_seller(self) -> None:
        """联系卖家"""
        orders = self.current_user.view_purchases(self.order_manager)
        accepted_orders = [o for o in orders if o.status == 'accepted']
        
        if not accepted_orders:
            print("\n暂无已接受的订单，无法联系卖家")
            return
        
        print("\n----- 已接受的订单 -----")
        for i, order in enumerate(accepted_orders, 1):
            print(f"\n{i}. 订单ID: {order.order_id}")
            print(f"商品: {order._product_name}")
        
        choice = input("\n请选择要联系卖家的订单序号 (0 退出): ")
        if choice == '0':
            return
        
        try:
            index = int(choice) - 1
            if 0 <= index < len(accepted_orders):
                order = accepted_orders[index]
                contact_info, message = self.current_user.contact_seller(self.order_manager, order.order_id)
                print(f"\n{message}")
                if contact_info:
                    print("\n----- 商家联系方式 -----")
                    print(f"商家: {contact_info['merchant']['name']}")
                    print(f"店铺: {contact_info['merchant']['shop']}")
                    print(f"联系方式: {contact_info['merchant']['contact']}")
            else:
                print("无效的选择")
        except:
            print("请输入有效的数字")


if __name__ == "__main__":
    app = OnlineMallApp()
    app.run()