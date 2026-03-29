import json
import os
from typing import Dict, List, Optional, Tuple, Any
from models.user import User, Merchant, Customer


class UserManager:
    def __init__(self, data_file: str = None):
        if data_file is None:
            # 使用相对路径，确保在不同操作系统上可移植
            current_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            self.data_file: str = os.path.join(current_dir, "data", "users.json")
        else:
            self.data_file: str = data_file
        self.users: Dict[str, User] = {}
        self.load_users()
    
    def load_users(self) -> None:
        """从文件加载用户数据"""
        if os.path.exists(self.data_file):
            try:
                with open(self.data_file, 'r', encoding='utf-8') as f:
                    users_data: List[Dict[str, Any]] = json.load(f)
                    for user_data in users_data:
                        try:
                            if user_data.get('role') == 'merchant':
                                user = Merchant(
                                    user_data['username'],
                                    user_data['password'],
                                    user_data['email'],
                                    user_data['shop_name'],
                                    user_data['contact_info']
                                )
                                # 恢复用户ID并同步更新merchant_id
                                user._user_id = user_data['user_id']
                                user._merchant_id = user_data['user_id']
                            else:
                                user = Customer(
                                    user_data['username'],
                                    user_data['password'],
                                    user_data['email'],
                                    user_data['shipping_address'],
                                    user_data['phone_number']
                                )
                                # 恢复用户ID
                                user._user_id = user_data['user_id']
                            self.users[user.user_id] = user
                        except Exception as e:
                            print(f"加载用户数据失败: {user_data.get('username', 'Unknown')}, 错误: {e}")
                            continue
            except Exception as e:
                print(f"加载用户数据文件失败: {e}")
                self.users = {}
    
    def save_users(self) -> None:
        """保存用户数据到文件"""
        users_data: List[Dict[str, Any]] = []
        for user in self.users.values():
            user_dict: Dict[str, Any] = {
                'user_id': user.user_id,
                'username': user.username,
                'password': user._password,  # 实际应用中应加密
                'email': user.email,
                'role': user.role
            }
            if user.role == 'merchant':
                user_dict['shop_name'] = user.shop_name
                user_dict['contact_info'] = user.contact_info
            else:
                user_dict['shipping_address'] = user.shipping_address
                user_dict['phone_number'] = user.phone_number
            users_data.append(user_dict)
        
        # 确保目录存在
        os.makedirs(os.path.dirname(self.data_file), exist_ok=True)
        
        with open(self.data_file, 'w', encoding='utf-8') as f:
            json.dump(users_data, f, ensure_ascii=False, indent=2)
    
    def register_user(self, username: str, password: str, email: str, role: str, **kwargs: Dict[str, Any]) -> Tuple[Optional[User], str]:
        """注册新用户"""
        # 检查用户名是否已存在
        for user in self.users.values():
            if user.username == username:
                return None, "用户名已存在"
        
        # 创建用户
        if role == 'merchant':
            shop_name = kwargs.get('shop_name')
            contact_info = kwargs.get('contact_info')
            if not shop_name or not contact_info:
                return None, "商家注册需要提供店铺名称和联系信息"
            user = Merchant(username, password, email, shop_name, contact_info)
        else:
            shipping_address = kwargs.get('shipping_address')
            phone_number = kwargs.get('phone_number')
            if not shipping_address or not phone_number:
                return None, "顾客注册需要提供收货地址和手机号码"
            user = Customer(username, password, email, shipping_address, phone_number)
        
        # 保存用户
        if user.register():
            self.users[user.user_id] = user
            self.save_users()
            return user, "注册成功"
        return None, "注册失败"
    
    def login_user(self, username: str, password: str) -> Tuple[Optional[User], str]:
        """用户登录"""
        for user in self.users.values():
            if user.login(username, password):
                return user, "登录成功"
        return None, "用户名或密码错误"
    
    def get_user_by_id(self, user_id: str) -> Optional[User]:
        """根据ID获取用户"""
        return self.users.get(user_id)
    
    def get_user_by_username(self, username: str) -> Optional[User]:
        """根据用户名获取用户"""
        for user in self.users.values():
            if user.username == username:
                return user
        return None